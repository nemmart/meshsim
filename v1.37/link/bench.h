// bench.h: one link on the bench: two link ends, the wire between them in each direction, a queue behind each
// stream at each end, a source in front of each, and the monitor. Everything that is not the link end is as simple
// as it can be: the queues are counts, a source hands over numbered packets, and the monitor knows what every
// delivered word must be.
//
// The same bench runs a kept case (cases/, a script of events), a random run and the search: all three are a
// config and a list of things done to it from outside, between cycles.
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "link_end.h"

namespace hl {

// What the wire does to one packet (aimed damage; NOTES.md section 7). The forged ones change a field and seal the
// header again, so that it passes its CRC: a wire makes one in 2^32, and they are reported apart.
enum class Fate : uint8_t {
    WHOLE, GONE, FIRST0, HEADER, DATA,
    HEAD0,                                 // every byte of the header zero, the data whole: a burst that reads as zeros and ends
                                           // where the data starts. The receiver sees idle, and then the data
    FORGE_ACK, FORGE_STOP, FORGE_LOST, FORGE_RECOVERY, FORGE_RESEND, FORGE_NUMBER,
    FORGE_NUMBER_BACK,                     // the packet's number one less: a later packet taken for the one before it
    FORGE_ACK_DATA,                        // a forged acknowledge field on a packet whose data is damaged too: must change nothing
    FORGE_STOP_BG,                         // the background's stop bit turned over
    FORGE_WRONG,                           // a reserved bit set: to its receiver, a packet made wrong
    FORGE_LONG,                            // a length of 33
    FORGE_RESTART,                         // "restart" and "lost" set on a packet that is not the restart packet: must change nothing
    COUNT
};
const char* fate_name(Fate f);
bool fate_from(const std::string& name, Fate& f);

// One direction of the link: a delay line, and the damage.
struct Wire {
    std::vector<uint8_t> ring;             // the bytes on their way, the oldest at pos
    uint32_t pos = 0;
    uint32_t at = 0, len = 0;              // the packet passing: its next byte's index, and its bytes
    Fate fate = Fate::WHOLE;
    uint8_t forged[HEADER_BYTES] = {};
    uint8_t hdr[HEADER_BYTES] = {};        // the passing packet's header as sent (from the tap)
    uint32_t ordinal = 0;                  // the packets started so far: how a script names one
    // Noise, set from outside for the next byte only, and what it did: not state.
    uint8_t next_xor = 0, next_or = 0; bool next_zero = false, next_random = false; uint8_t random_byte = 0;
    bool changed = false;                  // the last byte pushed was changed

    explicit Wire(unsigned delay = 1) : ring(delay, 0) {}
    uint8_t out() const { return ring[pos]; }
    void push(uint8_t b, const Tap& tap);
    void set_fate(Fate f, const Numbers& nums);     // just after the push of a packet's first byte
    static bool forges(Fate f) { return f > Fate::HEAD0; }
    static bool excuses(Fate f) { return forges(f) && f != Fate::FORGE_ACK_DATA && f != Fate::FORGE_RESTART; }   // may be believed
    bool idle() const { return at >= len; }
    bool noise();                                   // a byte put into the idle wire, where the last byte went
    template <class A> void io(A& a) {
        std::vector<uint8_t> turned(ring.size());
        for (size_t i = 0; i < ring.size(); i++) turned[i] = ring[(pos + i) % ring.size()];
        a.bytes(turned, turned.size());
        ring = turned; pos = 0;
        a(at); a(len); a(fate); a(forged); a(hdr); a(ordinal);
    }
    void canon(Writer& a) const {
        for (size_t i = 0; i < ring.size(); i++) a(ring[(pos + i) % ring.size()]);
        const uint32_t left = len - at; a(left);
        if (left) { a(at); a(fate); if (forges(fate)) a(forged); }
        if (at == 1) a(hdr);               // just started: its fate may still be chosen
    }
private:
    uint8_t damaged(uint32_t index, uint8_t b) const;
};

struct Config {
    Params p;                              // both ends alike
    unsigned queue_words = 1024;           // each stream's queue at each end, 16 KB (owner, 2026-11-17); "stop" at half
    unsigned window0 = 0;                  // end 0's window, if not p's: the host's
    unsigned wrong = 0;                    // end 0's foreground packet of this place (from 1) is made with hops 15, and end 1
                                           // is told it would forward: a packet its own sender made wrong (HL, "A packet made wrong")
    unsigned still = 0;                    // the cycles a sender may wait for an acknowledgement on a link in step; 0: the default
    unsigned delay = 1;                    // the wire's, in cycles, each way
    unsigned words = 32;                   // the words in each packet a source makes
    unsigned packets[2][STREAMS] = {};     // how many packets each source has: [end][stream]
    unsigned images = 0;                   // 1: every word a source makes is the image of a good header; 2: a packet's first
                                           // word is zeros and the rest are images: the longest run of zeros before one; 3: a
                                           // packet's first word is the image of the header of a packet that holds the rest;
                                           // 4: data made on purpose by one who knows the header it will travel under: with
                                           // the mask on it, the bytes on the wire are a packet (made_on_purpose)
};

struct Source { uint32_t total = 0, made = 0, avail = 0; };   // its packets; those taken by the end; those let out so far

class Link1 {
public:
    Config cfg;
    LinkEnd end[2];
    Wire wire[2];                          // wire[e] carries end e's bytes to the other end
    Source src[2][STREAMS];
    uint32_t fill[2][STREAMS] = {};        // the words waiting in each queue: [end][stream]
    uint32_t most_fill[2][STREAMS] = {};
    uint64_t delivered[2][STREAMS] = {};   // the words delivered into each queue: the next is this index of its flow
    bool gaps_allowed = false;             // after a restart words may be missing, never repeated or out of order
    bool fresh[2][STREAMS] = {};           // end e was powered on: what it had is gone, and its next word may be any of its flow
    bool must_report = false;
    bool excused = false;                  // something happened that may end in the link starting again: a forged header that
                                           // was believed, a packet made wrong, a power-on. Without it, starting again is a failure
    bool flipped = false;                  // a bit was flipped in a sender's memory: a wrong word may be delivered, and is counted
    uint64_t wrong_words = 0;
    uint64_t lost_words = 0;               // the words missing after a restart (a count, not state)
    uint32_t alarm[2][2] = {};             // end e raised the error for a packet no longer kept: the numbers it had let go that
                                           // the other end had not taken, after [0] up to [1]; 0 0 for none. If the other end
                                           // then takes one of them, nothing was lost, and the error was false
    bool stopped[2][STREAMS] = {};         // the stop bit end e last took, each stream: it must not start data there
    uint32_t waited[2] = {};               // the cycles end e has had packets kept, all sent, the link in step, and no acknowledgement
    uint32_t most_waited = 0;
    uint32_t still_bound() const { return cfg.still ? cfg.still : 8 * (cfg.p.status_timer + cfg.p.zeros + 2 * (HEADER_BYTES + cfg.p.max_words * WORD_BYTES)); }              // an end started the link again: an error must be up at an end that was not powered on
    bool power[2] = {};                    // an end starts again from power-on in the next cycle
    uint32_t budget[3] = {};               // the search's: packets it may still damage, bytes of noise, forged headers
    uint64_t t = 0;
    std::string bad;                       // the first property that failed, or empty
    bool stop = false;                     // something happened in the last cycle that the search stops at
    bool started_[2] = {};                 // end e started a packet in the last cycle
    bool started(unsigned w) const { return started_[w]; }

    explicit Link1(const Config& c);
    void step();

    // From outside, between cycles.
    void offer(unsigned e, unsigned s) { if (src[e][s].avail < src[e][s].total) src[e][s].avail++; }
    void drain(unsigned e, unsigned s, uint32_t n) { fill[e][s] -= n == 0 || n > fill[e][s] ? fill[e][s] : n; }
    void fate(unsigned w, Fate f) { wire[w].set_fate(f, end[w].nums); if (Wire::excuses(f)) excused = true; }
    bool flip(unsigned e);                 // a bit of the oldest packet end e keeps: a memory's fault, not the wire's
    void word(unsigned from, unsigned stream, uint64_t index, uint8_t out[WORD_BYTES]) const;   // what a flow carries at an index

    // The same names as the chain's bench (chain.h), for the search.
    unsigned n_wires() const { return 2; }
    Wire& the_wire(unsigned w) { return wire[w]; }
    unsigned n_sources() const { return 2 * STREAMS; }
    bool offerable(unsigned i) const { const Source& so = src[i / 2][i % 2]; return so.avail < so.total && so.made == so.avail; }
    void offer(unsigned i) { offer(i / 2, i % 2); }
    unsigned n_queues() const { return 2 * STREAMS; }
    uint32_t qfill(unsigned q) const { return fill[q / 2][q % 2]; }
    void drain(unsigned q, uint32_t n) { drain(q / 2, q % 2, n); }
    static constexpr bool flat = false;    // a source and a queue are an end and a stream

    // Everything sent has arrived and been acknowledged, and both ends are in step. With a stream held: everything
    // of the other stream.
    bool done(int hold = -1) const;
    // From here nothing more goes wrong: every source lets everything out, every queue is emptied every cycle, the
    // wire is whole. Returns the cycles it took to be done, or -1 if it is not within the bound (bad then says so).
    // With a stream held, that stream's queues are never emptied again, at either end: it stays stopped for good,
    // and the other stream must still finish (HL: "a stopped stream cannot block the other").
    long quiet(uint64_t bound, int hold = -1);
    FILE* trace = nullptr;                 // if set, a line for each thing that happens

    template <class A> void io(A& a) {
        for (auto& e : end) e.io(a);
        for (auto& w : wire) w.io(a);
        for (auto& e : src) for (auto& s : e) { a(s.made); a(s.avail); }
        a(fill); a(delivered); a(gaps_allowed); a(fresh); a(must_report); a(excused); a(flipped); a(alarm); a(stopped); a(waited); a(power); a(budget); a(t); a(started_);
    }
    // What the search knows a state by: the hash of everything that can still matter (LinkEnd::canon).
    Hash128 canon() const;
    std::vector<uint8_t> whole();          // the same with nothing left out but the cycle number: to check canon()
    std::vector<uint8_t> save();
    void load(const std::vector<uint8_t>& bytes);

private:
    void deliver(unsigned e);
};

// The word a flow carries at an index: never zero, and different at every index and flow.
void flow_word(unsigned from, unsigned stream, uint64_t index, uint8_t out[WORD_BYTES]);
// Data that defeats the mask, for one who knows every field of the header a packet will go out under but its data
// checksum: `outer` holds those fields (its length is the data's words). The data returned is such that, masked from
// that header's CRC, the bytes on the wire are a whole packet: a header for stream `outer.stream` with the number
// `inner_number`, and words - 1 words that pass its checksum. The CRC and the mask's sequence are both linear, so the
// data is solved for, not searched. Returns false if this try has no solution (another `salt` gives another try).
bool made_on_purpose(Header outer, uint32_t inner_number, uint64_t salt, uint8_t* data);

// ---- a kept case: a config, a list of events, and what must come of it under one or more sets of rules ----

struct Event {
    enum What : uint8_t { FATE, NOISE, OFFER, DRAIN, POWER, FLIP } what;
    uint64_t cycle = 0;                    // FATE: the packet's ordinal on its wire, from 1
    uint8_t a = 0, b = 0;                  // the wire or the end; the stream
    uint32_t n = 0;                        // FATE: the fate; DRAIN: the words, 0 for all
};
struct Run { std::string rules; bool ok = true; std::string text; };   // a rules line, and what is expected of it
struct Case {
    Config cfg;
    unsigned chain = 0;                    // not 0: a chain of this many nodes (chain.h), and events name sources, queues and wires by its numbers
    unsigned cpackets[3] = {};             // the chain's budgets: the host's foreground, its background, each node's replies
    unsigned fwd = 8;                      // the chain's forwarding buffers, in words
    std::vector<Event> events;
    uint64_t quiet_at = 0, bound = 100000;
    unsigned hold = 0;                     // not 0: from quiet_at on this stream (1 or 2) is never taken again, and the other must finish
    std::vector<Run> runs;
};

// "hl", then +name and -name for single rules, and ,name=value for sizes: one word.
bool apply_rules(const std::string& spec, Params& p, std::string& err);
std::string rules_text(const Params& p);          // the spec that gives p's rules, from "hl"
bool parse_case(const std::string& path, Case& c, std::string& err);
std::string case_text(const Case& c);
// Plays a case under a rules line: "" if every property held to the end, or the property that failed.
std::string play(const Case& c, const std::string& rules, Link1* out = nullptr, FILE* trace = nullptr);
std::string play_chain(const Case& c, const std::string& rules);     // chain.cpp: a case with "chain N"

}  // namespace hl
